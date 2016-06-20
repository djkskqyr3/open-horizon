//
// open horizon -- undefined_darkness@outlook.com
//

#include "main_window.h"
#include "scene_view.h"
#include "game/locations_list.h"
#include "game/objects.h"
#include "game/script.h"
#include "zip.h"
#include "extensions/zip_resources_provider.h"

//------------------------------------------------------------

namespace
{
enum modes
{
    mode_add = scene_view::mode_add,
    mode_edit = scene_view::mode_edit,
    mode_path = scene_view::mode_path,
    mode_zone = scene_view::mode_zone,
    mode_script = scene_view::mode_other,
    mode_info,

    modes_count
};
}

inline void alert(std::string message)
{
    auto m = new QMessageBox;
    m->setText(message.c_str());
    m->exec();
}

//------------------------------------------------------------

inline QTreeWidgetItem *new_tree_group(std::string name)
{
    auto group = new QTreeWidgetItem;
    group->setText(0, name.c_str());
    group->setFlags(Qt::ItemIsEnabled);
    return group;
}

//------------------------------------------------------------

inline QTreeWidgetItem *new_tree_item(std::string name)
{
    auto item = new QTreeWidgetItem;
    item->setText(0, name.c_str());
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    return item;
}

//------------------------------------------------------------

main_window::main_window(QWidget *parent): QMainWindow(parent)
{
    QSplitter *main_splitter = new QSplitter(this);
    setCentralWidget(main_splitter);

    m_objects_tree = new QTreeWidget;
    m_objects_tree->setHeaderLabel("Objects selection");
    m_objects_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_objects_tree, SIGNAL(itemSelectionChanged()), this, SLOT(on_tree_selected()));
    main_splitter->addWidget(m_objects_tree);

    m_scene_view = new scene_view(this);
    m_scene_view->update_objects_tree = std::bind(&main_window::update_objects_tree, this);
    main_splitter->addWidget(m_scene_view);

    m_navigator = new QTabWidget;
    main_splitter->addWidget(m_navigator);

    main_splitter->setSizes(QList<int>() << 200 << 1000 << 400);

    auto add_objects_tree = new QTreeWidget;
    add_objects_tree->setHeaderLabel("Objects");
    m_navigator->insertTab(mode_add, add_objects_tree, "Add");
    auto &obj_list = game::get_objects_list();
    for (auto &o: obj_list)
    {
        auto item = new_tree_item(o.id);
        if (o.group.empty())
        {
            add_objects_tree->addTopLevelItem(item);
            continue;
        }

        QList<QTreeWidgetItem*> items = add_objects_tree->findItems(o.group.c_str(), Qt::MatchExactly, 0);
        if (!items.empty())
        {
            items[0]->addChild(item);
            continue;
        }

        auto group = new_tree_group(o.group);
        add_objects_tree->addTopLevelItem(group);
        group->addChild(item);
    }
    connect(add_objects_tree, SIGNAL(itemClicked(QTreeWidgetItem*, int)), this, SLOT(on_add_tree_selected(QTreeWidgetItem*, int)));

    m_edit_layout = new QFormLayout;
    QWidget *edit_widget = new QWidget;
    m_navigator->insertTab(mode_edit, edit_widget, "Edit");
    edit_widget->setLayout(m_edit_layout);

    QWidget *path_widget = new QWidget;
    m_navigator->insertTab(mode_path, path_widget, "Path");

    QWidget *zone_widget = new QWidget;
    m_navigator->insertTab(mode_zone, zone_widget, "Zone");

    auto *script_layout = new QSplitter(Qt::Vertical);
    m_script_edit = new QTextEdit;
    connect(m_script_edit, SIGNAL(textChanged()), this, SLOT(on_script_changed()));
    new highlight_lua(m_script_edit->document());
    script_layout->addWidget(m_script_edit);
    m_script_errors = new QTextEdit;
    m_script_errors->setReadOnly(true);
    m_script_errors->setTextColor(Qt::red);
    script_layout->addWidget(m_script_errors);
    m_compile_timer = new QTimer(this);
    connect(m_compile_timer, SIGNAL(timeout()), this, SLOT(on_compile_script()));
    script_layout->setSizes(QList<int>() << 1000 << 100);
    m_navigator->insertTab(mode_script, script_layout, "Script");

    auto info_layout = new QFormLayout;
    QWidget *info_widget = new QWidget;
    m_navigator->insertTab(mode_info, info_widget, "Info");
    info_widget->setLayout(info_layout);

    QSignalMapper *m = new QSignalMapper(this);
    for (int i = 0; i < modes_count; ++i)
    {
        QShortcut *s = new QShortcut(QKeySequence(("Ctrl+" + std::to_string(i+1)).c_str()), this);
        connect(s, SIGNAL(activated()), m, SLOT(map()));
        m->setMapping(s, i);
    }
    connect(m, SIGNAL(mapped(int)), m_navigator, SLOT(setCurrentIndex(int)));
    connect(m_navigator, SIGNAL(currentChanged(int)), this, SLOT(on_mode_changed(int)));

    m_scene_view->set_mode(scene_view::mode_other);
    setup_menu();
}

//------------------------------------------------------------

void main_window::setup_menu()
{
    QMenu *file_menu = menuBar()->addMenu("File");

    QAction *new_mission = new QAction("New mission", this);
    new_mission->setShortcut(QKeySequence::New);
    this->addAction(new_mission);
    file_menu->addAction(new_mission);
    connect(new_mission, SIGNAL(triggered()), this, SLOT(on_new_mission()));

    QAction *load_mission = new QAction("Load mission", this);
    load_mission->setShortcut(QKeySequence::Open);
    this->addAction(load_mission);
    file_menu->addAction(load_mission);
    connect(load_mission, SIGNAL(triggered()), this, SLOT(on_load_mission()));

    QAction *save_mission = new QAction("Save mission", this);
    save_mission->setShortcut(QKeySequence::Save);
    this->addAction(save_mission);
    file_menu->addAction(save_mission);
    connect(save_mission, SIGNAL(triggered()), this, SLOT(on_save_mission()));

    QAction *save_as_mission = new QAction("Save as mission", this);
    save_as_mission->setShortcut(QKeySequence::SaveAs);
    this->addAction(save_as_mission);
    file_menu->addAction(save_as_mission);
    connect(save_as_mission, SIGNAL(triggered()), this, SLOT(on_save_as_mission()));
}

//------------------------------------------------------------

void main_window::on_new_mission()
{
    QStringList items;
    auto &list = game::get_locations_list();
    for (auto &l: list)
    {
        auto str = QString::fromWCharArray(l.second.c_str(), l.second.size());
        str.append((" [" + l.first + "]").c_str());
        items << str;
    }

    bool ok = false;
    QString item = QInputDialog::getItem(this, "Select location", "Location:", items, 0, false, &ok);
    if (!ok || item.isEmpty())
        return;

    const int idx = items.indexOf(item);
    if (idx < 0 || idx >= (int)list.size())
        return;

    m_filename.clear();
    clear_mission();
    m_location = list[idx].first;
    m_scene_view->load_location(m_location);
    update_objects_tree();

    m_script_edit->setText("--Open-Horizon mission script\n\n"
                           "function init()\n"
                           "    --do init here\n"
                           "end\n");

    m_navigator->setCurrentIndex(mode_info);
}

//------------------------------------------------------------

void main_window::on_load_mission()
{
    auto filename = QFileDialog::getOpenFileName(this, "Load mission", "missions", "*.zip");
    if (!filename.length())
        return;

    std::string filename_str = filename.toUtf8().constData();

    auto prov = &nya_resources::get_resources_provider();
    nya_resources::file_resources_provider fprov;
    nya_resources::set_resources_provider(&fprov);
    nya_resources::zip_resources_provider zprov;
    bool result = zprov.open_archive(filename_str.c_str());
    nya_resources::set_resources_provider(prov);
    if (!result)
    {
        alert("Unable to load location " + filename_str);
        return;
    }

    m_filename.assign(filename_str);

    pugi::xml_document doc;
    if (!load_xml(zprov.access("objects.xml"), doc))
        return;

    auto root = doc.first_child();
    std::string loc = root.attribute("location").as_string();
    if (loc.empty())
        return;

    clear_mission();

    m_location = loc;
    m_scene_view->load_location(loc);

    for (pugi::xml_node o = root.child("object"); o; o = o.next_sibling("object"))
    {
        scene_view::object obj;
        obj.name = o.attribute("name").as_string();
        obj.id = o.attribute("id").as_string();
        obj.yaw = o.attribute("yaw").as_float();
        obj.pos.set(o.attribute("x").as_float(), o.attribute("y").as_float(), o.attribute("z").as_float());
        obj.y = o.attribute("editor_y").as_float();
        obj.pos.y -= obj.y;
        m_scene_view->add_object(obj);
    }

    auto script_res = zprov.access("script.lua");
    if (script_res)
    {
        std::string script;
        script.resize(script_res->get_size());
        if (!script.empty())
            script_res->read_all(&script[0]);
        m_script_edit->setText(script.c_str());
        script_res->release();
    }

    update_objects_tree();
    m_navigator->setCurrentIndex(mode_info);
}

//------------------------------------------------------------

void main_window::on_save_mission()
{
    if (m_filename.empty())
    {
        on_save_as_mission();
        return;
    }

    zip_t *zip = zip_open(m_filename.c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 0);
    if (!zip)
    {
        alert("Unable to save mission " + m_filename);
        return;
    }

    std::string str = "<!--Open Horizon mission-->\n";
    str += "<mission location=\"" + m_location + "\">\n";
    for (auto &o: m_scene_view->get_objects())
    {
        str += "\t<object ";
        str += "name=\"" + o.name + "\" ";
        str += "id=\"" + o.id + "\" ";
        str += "x=\"" + std::to_string(o.pos.x) + "\" ";
        str += "y=\"" + std::to_string(o.pos.y + o.y) + "\" ";
        str += "z=\"" + std::to_string(o.pos.z) + "\" ";
        str += "yaw=\"" + std::to_string(o.yaw.get_deg()) + "\" ";
        str += "editor_y=\"" + std::to_string(o.y) + "\" ";
        str += "/>\n";
    }

    str += "</mission>\n";

    zip_entry_open(zip, "objects.xml");
    zip_entry_write(zip, str.c_str(), str.length());
    zip_entry_close(zip);

    std::string script = m_script_edit->toPlainText().toUtf8().constData();
    zip_entry_open(zip, "script.lua");
    zip_entry_write(zip, script.c_str(), script.size());
    zip_entry_close(zip);

    zip_close(zip);
}

//------------------------------------------------------------

void main_window::on_save_as_mission()
{
    auto filename = QFileDialog::getSaveFileName(this, "Save mission", "missions", "*.zip");
    if (!filename.length())
        return;

    m_filename.assign(filename.toUtf8().constData());
    on_save_mission();
}

//------------------------------------------------------------

void main_window::on_tree_selected()
{
    auto items = m_objects_tree->selectedItems();

    //ToDo

    if (items.empty())
        return;

    m_navigator->setCurrentIndex(mode_edit);

    for (auto &item: items)
    {
        if (item->text(0) == "player_spawn")
        {
            //ToDo
            continue;
        }

        for (int i = 0; i < m_objects_tree->topLevelItemCount(); ++i)
        {
           QTreeWidgetItem *p = m_objects_tree->topLevelItem(i);
           const int idx = p->indexOfChild(item);
           if (idx < 0)
               continue;

           if (p->text(0) == "objects")
           {
               //ToDo
           }
        }
    }
}

//------------------------------------------------------------

void main_window::on_add_tree_selected(QTreeWidgetItem* item, int)
{
    if (item && item->parent())
        m_scene_view->set_selected_add(item->text(0).toUtf8().constData());
    else
        m_scene_view->set_selected_add("");
}

//------------------------------------------------------------

void main_window::on_mode_changed(int idx)
{
    if (m_location.empty() || idx >= scene_view::mode_other)
        m_scene_view->set_mode(scene_view::mode_other);
    else
        m_scene_view->set_mode(scene_view::mode(idx));
}

//------------------------------------------------------------

void main_window::on_script_changed()
{
    m_compile_timer->start(1000);
}

//------------------------------------------------------------

void main_window::on_compile_script()
{
    m_compile_timer->stop();

    auto t = m_script_edit->toPlainText();
    game::script s;
    s.load(t.toUtf8().constData());
    m_script_errors->setText(s.get_error().c_str());
}

//------------------------------------------------------------

void main_window::update_objects_tree()
{
    m_objects_tree->clear();

    auto objects = new_tree_group("objects");
    m_objects_tree->addTopLevelItem(objects);

    for (auto &o: m_scene_view->get_objects())
        objects->addChild(new_tree_item(o.name + " (" + o.id + ")"));

    m_objects_tree->addTopLevelItem(new_tree_item("player spawn"));
    m_objects_tree->expandAll();
}

//------------------------------------------------------------

void main_window::clear_mission()
{
    m_scene_view->clear_objects();
}

//------------------------------------------------------------

highlight_lua::highlight_lua(QTextDocument *parent): QSyntaxHighlighter(parent)
{
    QColor keyword_color = QColor(0xcc, 0, 0xa1);
    std::string keywords[] = {"and",    "break",  "do",   "else",     "elseif",
                              "end",    "false",  "for",  "function", "if",
                              "in",     "local",  "nil",  "not",      "or",
                              "repeat", "return", "then", "true",     "until", "while"};
    for (auto k: keywords)
        m_rules.push_back({QRegExp(("\\b" + k + "\\b").c_str()), keyword_color});

    m_rules.push_back({QRegExp("[0-9]"), QColor(0x5c, 0, 0xdd)});

    m_rules.push_back({QRegExp("\\b[A-Za-z0-9_]+(?=\\()"), QColor(0x6f, 0, 0x8f)});

    m_rules.push_back({QRegExp("\".*\""), QColor(0xe4, 0, 0x41)});
    m_rules.push_back({QRegExp("\'.*\'"), QColor(0xe4, 0, 0x41)});

    m_comment_color = QColor(0, 0x8e, 0);
    m_rules.push_back({QRegExp("--[^\n]*"), m_comment_color});
    m_comment_start = QRegExp("--\\[\\[");
    m_comment_end = QRegExp("\\]\\]");
}

//------------------------------------------------------------

void highlight_lua::highlightBlock(const QString& text)
{
    for (auto &r: m_rules)
    {
        QRegExp expression(r.first);
        int index = expression.indexIn(text);
        while (index >= 0)
        {
            int length = expression.matchedLength();
            setFormat(index, length, r.second);
            index = expression.indexIn(text, index + length);
        }
    }

    setCurrentBlockState(0);

    int start = 0;
    if (previousBlockState() != 1)
        start = m_comment_start.indexIn(text);

    while (start >= 0)
    {
        int end = m_comment_end.indexIn(text, start);
        int len;
        if (end == -1)
        {
            setCurrentBlockState(1);
            len = text.length() - start;
        }
        else
            len = end - start + m_comment_end.matchedLength();

        setFormat(start, len, m_comment_color);
        start = m_comment_start.indexIn(text, start + len);
    }
}

//------------------------------------------------------------